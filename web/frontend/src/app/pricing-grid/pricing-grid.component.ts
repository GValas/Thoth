import { Component, OnInit, inject } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { MatCardModule } from '@angular/material/card';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatSelectModule } from '@angular/material/select';
import { MatInputModule } from '@angular/material/input';
import { MatButtonModule } from '@angular/material/button';
import { MatButtonToggleModule } from '@angular/material/button-toggle';
import { MatCheckboxModule } from '@angular/material/checkbox';
import { MatChipsModule } from '@angular/material/chips';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTooltipModule } from '@angular/material/tooltip';
import { MatDatepickerModule } from '@angular/material/datepicker';
import { provideNativeDateAdapter } from '@angular/material/core';
import { GridStateService } from './grid-state.service';
import { ResultsTableComponent } from './results-table.component';

//! Pricing-grid builder. All state lives in GridStateService (root-scoped) so the form,
//! results and any in-flight job survive tab navigation; this component is just the view.
//! Maturities are added via a date picker (chips); the contract currency is selectable.
@Component({
  selector: 'app-pricing-grid',
  standalone: true,
  imports: [
    FormsModule,
    MatCardModule,
    MatFormFieldModule,
    MatSelectModule,
    MatInputModule,
    MatButtonModule,
    MatButtonToggleModule,
    MatCheckboxModule,
    MatChipsModule,
    MatIconModule,
    MatProgressBarModule,
    MatTooltipModule,
    MatDatepickerModule,
    ResultsTableComponent,
  ],
  providers: [provideNativeDateAdapter()],
  templateUrl: './pricing-grid.component.html',
  styleUrl: './pricing-grid.component.scss',
})
export class PricingGridComponent implements OnInit {
  readonly s = inject(GridStateService);
  pickerDate: Date | null = null;

  ngOnInit(): void {
    this.s.init();
  }

  //! add the picked date as a maturity and clear the picker so it can pick again.
  addMaturity(d: Date | null): void {
    this.s.addMaturity(d);
    this.pickerDate = null;
  }
}
